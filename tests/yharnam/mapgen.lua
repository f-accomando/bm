-- The ground of the whole hunt as the chunks make it (the street plan of carts/yharnam/main.lua), written as
-- the cartridge's map: two CSV files of 256 x 256 cells, the layers "ground" and "overlay" (kerbs, grass edges,
-- puddles, leaves). It is the first drawn map, the one the SDK opens to be changed by hand (carts/yharnam/mkmap.py
-- runs it; the cartridge reads it: MAP.drawn in main.lua).
--
--   luahost tests/yharnam/mapgen.lua carts/yharnam/main.lua OUTDIR   -> OUTDIR/map_ground.csv, OUTDIR/map_overlay.csv
--
-- Every chunk of the map is made once in the fake bm with no map (the ring, as on a cartridge without a drawn
-- map) and its cells are read from the ring at once: ground at cells 0-127 of the ring's rows, overlay at 128-255.
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local SRC, OUT = arg[1], arg[2]
assert(SRC and OUT, "luahost tests/yharnam/mapgen.lua carts/yharnam/main.lua OUTDIR")
local Y = B.load(SRC)
local M, CS, env = Y.MAP, Y.CS, B.env
assert(not M.drawn and M.ring == 1, "the fake bm has no drawn map: the ring is layer 1")
local RING = 4                                -- (main.lua's: the ring holds RING x RING chunks)
local CW = CS * 2                             -- cells across a chunk
local N = M.size * CW                         -- cells across the map
local ground, over = {}, {}
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
  end
end

local function write(name, cells, what)
  local path = OUT .. "/map_" .. name .. ".csv"
  local f = assert(io.open(path, "wb"))
  f:write("# Yharnam's map, layer \"" .. name .. "\": " .. what .. ". " .. N .. " x " .. N .. " cells of 8 x 8 (sheet.png);\n")
  f:write("# drawn in the SDK (F3 F3), taken back with carts/yharnam/mkmap.py --from\n")
  local row = {}
  for y = 0, N - 1 do
    for x = 0, N - 1 do row[x + 1] = cells[y * N + x] end
    f:write(table.concat(row, ","), "\n")
  end
  f:close()
  print(path)
end
write("ground", ground, "the ground")
write("overlay", over, "what lies on the ground (kerbs, grass edges, puddles, leaves)")
