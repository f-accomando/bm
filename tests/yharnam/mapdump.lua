-- The map of the hunt as text (the cell grid of the areas, the chunk layout, lamps, bosses, gate pairs), for
-- carts/yharnam/build/mapview.py:
--   luahost tests/yharnam/mapdump.lua carts/yharnam/main.lua | python3 carts/yharnam/build/mapview.py out.png
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local Y = B.load(arg[1])
local M = Y.MAP
print("size " .. M.size)
for i, a in ipairs(M.areas) do print(string.format("area %d %s %s", i, a.id, a.name:gsub(" ", "_"))) end
for r, row in ipairs(M.grid) do print("cell " .. (r - 1) .. " " .. row) end
for r, row in ipairs(M.layout) do print("row " .. (r - 1) .. " " .. row) end
for i, a in ipairs(M.areas) do
  for k, l in ipairs(a.lamps) do print(string.format("lamp %d %d %d %d", l[1], l[2], k, i)) end
  print(string.format("boss %d %d %s", a.bc[1], a.bc[2], a.boss))
end
for _, g in ipairs(M.gates) do
  print(string.format("gate %s %s %s %s %d,%d,%d,%d", g.id, g.type, g.requires or "-",
                      g.from and M.areas[g.from].id or "-", g.pair[1], g.pair[2], g.pair[3], g.pair[4]))
end
