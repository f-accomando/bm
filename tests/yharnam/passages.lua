-- The passages between the areas, with their gates open: can the hunter walk through each one, and how wide is it?
--   luahost tests/yharnam/passages.lua bm/yharnam/main.lua [--drawn]   -> "passages ok", else the narrow ones and exit 1
-- Every chunk is made and every gate is broken (as after the boss). Then the real collision of the game (Y.blocked: the
-- tiles of the ground and the ellipses of the objects, for the hunter's feet) is asked on a grid of 2 pixels round each
-- gate: a fill from the side A of the border has to reach the side B, and the free width of the border line (the
-- narrowest of the lines 0, 8 and 16 pixels from the border, on both sides) has to be at least MIN pixels.
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local SRC = arg[1]
assert(SRC, "luahost tests/yharnam/passages.lua bm/yharnam/main.lua [--drawn]")
if arg[2] == "--drawn" then
  local A = (arg[0]:match("^(.*/)") or "") .. "../../bm/yharnam/assets/"
  B.load_map({ { "ground", A .. "map_ground.csv" }, { "overlay", A .. "map_overlay.csv" },
               { "objects", A .. "map_objects.csv" } })
end
local Y = B.load(SRC)
local M, CS, TS = Y.MAP, Y.CS, Y.TS
local MIN = 20                                 -- the hunter is 12 wide: the free width of the walk (his centre) is at least 20
local CELL = 8 * TS                            -- a cell of MAP.grid: 8 tiles
local floor, abs = math.floor, math.abs


local fails = 0
local function report(id, vert, X, lo, hi)
  -- vert: the border is a vertical line x = X, crossed in x; lo..hi: the span of the border along y (pixels)
  local function bl(a, c) if vert then return Y.blocked(a, c) else return Y.blocked(c, a) end end   -- a: across, c: along
  local function free(a, c) return not bl(a, c) end
  -- the free width of the lines at d pixels from the border: the longest run of free points along the span
  local widest_min, where = 1e9, nil
  for _, d in ipairs({ -16, -8, 0, 8, 16 }) do
    local best, run = 0, 0
    for c = lo, hi, 2 do
      if free(X + d, c) then run = run + 2; if run > best then best = run end else run = 0 end
    end
    if best < widest_min then widest_min, where = best, d end
  end
  -- the fill from the side A (X - 40) to the side B (X + 40), on the grid of 2 pixels, inside the span
  local x0, x1 = X - 60, X + 60
  local w = (x1 - x0) // 2 + 1
  local seen, stack = {}, {}
  for c = lo, hi, 2 do
    for a = x0, X - 36, 2 do
      if free(a, c) then
        local k = ((a - x0) // 2) * 100000 + (c - lo) // 2
        if not seen[k] then seen[k] = true; stack[#stack + 1] = { a, c } end
      end
    end
  end
  local reached = false
  while #stack > 0 do
    local p = table.remove(stack)
    if p[1] >= X + 36 then reached = true; break end
    for _, d in ipairs({ { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } }) do
      local a, c = p[1] + d[1], p[2] + d[2]
      if a >= x0 and a <= x1 and c >= lo and c <= hi and free(a, c) then
        local k = ((a - x0) // 2) * 100000 + (c - lo) // 2
        if not seen[k] then seen[k] = true; stack[#stack + 1] = { a, c } end
      end
    end
  end
  local ok = reached and widest_min >= MIN
  print(string.format("%-26s %s  free width %3d px (at %+d)  %s", id, vert and "x=" .. X or "y=" .. X, widest_min, where or 0,
                      reached and "crossed" or "NOT CROSSED"))
  if not ok then fails = fails + 1 end
end

-- open every gate, as after the boss: the chunks that are made later have their gates wrecked (the game keeps only the
-- chunks near the hunter: the ones round a gate are made again just before its test)
for _, g in ipairs(M.gates) do M.G.gates[g.id] = true end
for _, c in ipairs(M.cuts) do M.G.gates[c.g.id] = true end
local function near(px, py)
  local cx, cy = floor(px / (CS * TS)), floor(py / (CS * TS))
  for dy = -1, 1 do for dx = -1, 1 do
    if cx + dx >= 0 and cy + dy >= 0 and cx + dx < M.size and cy + dy < M.size then Y.ensure(cx + dx, cy + dy) end
  end end
end

for _, g in ipairs(M.gates) do
  local p = g.pair
  if p[1] ~= p[3] then       -- cells side by side: the border is the vertical line between them
    local X = math.max(p[1], p[3]) * CELL
    near(X, p[2] * CELL + CELL // 2)
    report(g.id, true, X, p[2] * CELL, p[2] * CELL + CELL - 2)
  else                       -- one above the other: a horizontal line
    local Yb = math.max(p[2], p[4]) * CELL
    near(p[1] * CELL + CELL // 2, Yb)
    report(g.id, false, Yb, p[1] * CELL, p[1] * CELL + CELL - 2)
  end
end
for _, c in ipairs(M.cuts) do
  local X = c.at * TS + TS // 2
  local mid = (c.gap[1] + c.gap[2]) // 2 * TS
  if c.vert then near(X, mid) else near(mid, X) end
  report(c.id, c.vert, X, c.gap[1] * TS, c.gap[2] * TS + TS - 2)
end

if fails > 0 then print("passages: " .. fails .. " narrow or closed"); os.exit(1) end
print("passages ok")
