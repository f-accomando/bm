-- The whole hunt as the chunks make it (the street plan of carts/yharnam/main.lua), written as the cartridge's
-- map: three CSV files of 256 x 256 cells, the layers "ground", "overlay" (kerbs, grass edges, puddles, leaves)
-- and "objects" (a placeholder, art/places.py, on the tile of every house, tree, lamp, grave and thing the chunks
-- make: their units). It is the first drawn map, the one the SDK opens to be changed by hand
-- (carts/yharnam/mkmap.py runs it; the cartridge reads it: MAP.drawn, MAP.objects in main.lua). With it the game
-- makes the same town (tests/yharnam/drawn.lua checks it chunk by chunk).
--
--   luahost tests/yharnam/mapgen.lua carts/yharnam/main.lua OUTDIR   -> OUTDIR/map_{ground,overlay,objects}.csv
--
-- Every chunk of the map is made once in the fake bm with no map (the ring, as on a cartridge without a drawn
-- map) and its cells are read from the ring at once: ground at cells 0-127 of the ring's rows, overlay at 128-255.
-- Two units on one tile cannot both have a placeholder: that stops it (the plan would need a change).
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local SRC, OUT = arg[1], arg[2]
assert(SRC and OUT, "luahost tests/yharnam/mapgen.lua carts/yharnam/main.lua OUTDIR")
local Y = B.load(SRC)
local M, CS, env = Y.MAP, Y.CS, B.env
assert(not M.drawn and M.ring == 1, "the fake bm has no drawn map: the ring is layer 1")
local RING = 4                                -- (main.lua's: the ring holds RING x RING chunks)
local SW8 = 4096 // 8                         -- (main.lua's SHEET_W: cells a sheet row)
local CW = CS * 2                             -- cells across a chunk
local N = M.size * CW                         -- cells across the map
local PLACE = {}
for c, k in pairs(M.kind_at) do PLACE[k] = c end
local ground, over, objects, what = {}, {}, {}, {}
local function place(k, tx, ty)               -- a placeholder on world tile (tx, ty): its 2 x 2 cells
  local c, x, y = assert(PLACE[k], "no placeholder " .. k), tx * 2, ty * 2
  objects[y * N + x], objects[y * N + x + 1], objects[(y + 1) * N + x], objects[(y + 1) * N + x + 1] =
    c, c + 1, c + SW8, c + SW8 + 1
end
local units, clashes = 0, {}
for cy = 0, M.size - 1 do
  for cx = 0, M.size - 1 do
    local ch = Y.ensure(cx, cy)
    assert(ch.ready and ch.cx == cx and ch.cy == cy, "chunk " .. cx .. "," .. cy .. " made")
    local rx0, ry0 = (cx % RING) * CW, (cy % RING) * CW
    for j = 0, CW - 1 do
      for i = 0, CW - 1 do
        local k = (cy * CW + j) * N + cx * CW + i
        ground[k] = env.mget(rx0 + i, ry0 + j, 1)
        over[k] = env.mget(128 + rx0 + i, ry0 + j, 1)
        assert(ground[k] > 0, "the ground has no hole at " .. (cx * CW + i) .. "," .. (cy * CW + j))
      end
    end
    -- the units still there (a boss's arena drops a fifth of its things): a placeholder each
    local alive = {}
    for _, o in ipairs(ch.objs) do if o.u then alive[o.u] = true end end
    for _, l in ipairs(ch.lights) do if l.u then alive[l.u] = true end end
    for _, u in ipairs(ch.units) do
      if alive[u] then
        units = units + 1
        local tx, ty = cx * CS + u.lx, cy * CS + u.ly
        local tiles = { { u.k, tx, ty } }       -- (a house: its corner, the roof right of it, the body under)
        for y = 0, (u.h or 1) - 1 do
          for x = 0, (u.w or 1) - 1 do
            if x > 0 or y > 0 then tiles[#tiles + 1] = { y == 0 and "roof" or "body", tx + x, ty + y } end
          end
        end
        for _, t in ipairs(tiles) do
          local key = t[3] * N + t[2]
          if what[key] then clashes[#clashes + 1] = string.format("%d,%d: %s and %s", t[2], t[3], what[key], t[1]) end
          what[key] = t[1]
          place(t[1], t[2], t[3])
        end
      end
    end
  end
end
if #clashes > 0 then
  io.stderr:write("two units on one tile (" .. #clashes .. "):\n  " .. table.concat(clashes, "\n  ") .. "\n")
  os.exit(1)
end

local function write(name, cells, about)
  local path = OUT .. "/map_" .. name .. ".csv"
  local f = assert(io.open(path, "wb"))
  f:write("# Yharnam's map, layer \"" .. name .. "\": " .. about .. ". " .. N .. " x " .. N .. " cells of 8 x 8 (sheet.png);\n")
  f:write("# drawn in the SDK (F3 F3), taken back with carts/yharnam/mkmap.py --from\n")
  local row = {}
  for y = 0, N - 1 do
    for x = 0, N - 1 do row[x + 1] = cells[y * N + x] or 0 end
    f:write(table.concat(row, ","), "\n")
  end
  f:close()
  print(path)
end
write("ground", ground, "the ground")
write("overlay", over, "what lies on the ground (kerbs, grass edges, puddles, leaves)")
write("objects", objects, "where the houses, trees, lamps, graves and things stand (placeholders, not drawn)")
print(units .. " units")
