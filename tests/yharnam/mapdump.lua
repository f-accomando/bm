-- The map of the hunt as text (areas with their layouts, lamps, gates), for carts/yharnam/mapview.py:
--   luahost tests/yharnam/mapdump.lua carts/yharnam/main.lua | python3 carts/yharnam/mapview.py out.png
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local Y = B.load(arg[1])
local M = Y.MAP
print("size " .. M.size)
for i, a in ipairs(M.areas) do
  print(string.format("area %d %s %d %d %d %d %s", i, a.id, a.x0, a.y0, a.x1, a.y1, a.name:gsub(" ", "_")))
  for r, row in ipairs(a.layout) do print("row " .. i .. " " .. (a.y0 + r - 1) .. " " .. a.x0 .. " " .. row) end
  for k, l in ipairs(a.lamps) do print(string.format("lamp %d %d %d", l[1], l[2], k)) end
  print(string.format("boss %d %d %s", a.bc[1], a.bc[2], a.boss))
end
for _, g in ipairs(M.gates) do
  local e = {}
  for _, c in ipairs(g.edge) do
    for _, d in ipairs({ { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }) do
      local ia, ib = M.area_of(c[1], c[2]), M.area_of(c[1] + d[1], c[2] + d[2])
      if ib and ia ~= ib and M.gate_at(c[1], c[2], c[1] + d[1], c[2] + d[2]) == g then
        e[#e + 1] = string.format("%d,%d,%d,%d", c[1], c[2], d[1], d[2])
      end
    end
  end
  print(string.format("gate %s %s %s %s %s", g.id, g.type, g.requires or "-", g.from and M.areas[g.from].id or "-",
                      table.concat(e, ";")))
end
