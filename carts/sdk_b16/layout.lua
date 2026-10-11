-- layout: the screen of the bm SDK (RGB30) cut into regions on the text
-- grid (8 x 16 pixels: 45 columns and 22 rows at 360x360). Internal to
-- this cartridge (carts/sdk_b16/build.py puts it before main.lua as the
-- local `layout`); tests/studio/sdk_b16_host.lua tries it on its own.
--
--   L.rect(x, y, w, h)          a region {x, y, w, h}
--   L.split(r, "v", sizes)      r cut top to bottom ("h": left to right):
--                               a size is pixels (16), rows or columns of
--                               text ("3r", "20c"), or a share of what is
--                               left ("*", "2*"); shares fall on the grid
--                               (whole columns, whole rows) and the last one
--                               takes the rest. Returns the regions, in order
--   L.narrow([w])               a screen too narrow for panels side by side
--                               (under 480 pixels): the pages stack them
--   L.cols(r), L.rows(r)        whole columns and rows of text in r
--   L.row(r, i)                 x, y of the text row i (1 = the first) of r
--   L.snap(x)                   x on the next column (text starts there)
--   L.fit(s, cols)              s cut to cols characters
--   L.wrap(s, cols, [n])        s in lines of at most cols characters, cut
--                               between words (at most n lines)
--   L.inside(a, b), L.overlap(a, b)   for the checks of the tests

local L = { CW = 8, CH = 16 }

function L.rect(x, y, w, h) return { x = x, y = y, w = w, h = h } end

function L.snap(x) return (x + L.CW - 1) // L.CW * L.CW end
function L.cols(r) return r.w // L.CW end
function L.rows(r) return r.h // L.CH end
function L.row(r, i) return r.x, r.y + (i - 1) * L.CH end
function L.narrow(w) return (w or SCREEN_W or 640) < 480 end

-- a size of a split: pixels, or nil and the weight of a share
local function fixed(s, unit)
  if type(s) == "number" then return s end
  local n, u = tostring(s):match("^(%d+)([rc])$")
  if n then return tonumber(n) * (u == "r" and L.CH or L.CW) end
  local k = tostring(s):match("^(%d*)%*$")
  if not k then error("layout: a size is pixels, \"3r\", \"20c\" or \"*\": " .. tostring(s), 3) end
  return nil, tonumber(k ~= "" and k or "1")
end

function L.split(r, dir, sizes)
  local v = dir == "v"
  if not v and dir ~= "h" then error("layout: split \"v\" or \"h\", not " .. tostring(dir), 2) end
  local total, unit = v and r.h or r.w, v and L.CH or L.CW
  local px, weight, used, shares = {}, {}, 0, 0
  for i, s in ipairs(sizes) do
    px[i], weight[i] = fixed(s)
    if px[i] then used = used + px[i] else shares = shares + weight[i] end
  end
  if used > total then error(string.format("layout: %d pixels do not fit in %d", used, total), 2) end
  -- the shares: whole cells of the grid, the last share the rest
  local free, last = total - used, nil
  for i = #sizes, 1, -1 do if not px[i] then last = i; break end end
  local given = 0
  for i in ipairs(sizes) do
    if not px[i] and i ~= last then
      px[i] = free * weight[i] // shares // unit * unit
      given = given + px[i]
    end
  end
  if last then px[last] = free - given end
  local out, at = {}, v and r.y or r.x
  for i in ipairs(sizes) do
    if v then out[i] = L.rect(r.x, at, r.w, px[i]) else out[i] = L.rect(at, r.y, px[i], r.h) end
    at = at + px[i]
  end
  return out
end

function L.fit(s, cols)
  s = tostring(s)
  if cols <= 0 then return "" end
  if #s <= cols then return s end
  if LAYOUT_CUT then LAYOUT_CUT(s, cols) end   -- (the tests list what was cut)
  return s:sub(1, cols)
end

function L.wrap(s, cols, n)
  local out, line = {}, ""
  for word in tostring(s):gmatch("%S+") do
    while #word > cols do                      -- a word longer than the line: cut
      if line ~= "" then out[#out + 1] = line; line = "" end
      out[#out + 1] = word:sub(1, cols)
      word = word:sub(cols + 1)
    end
    if line == "" then line = word
    elseif #line + 1 + #word <= cols then line = line .. " " .. word
    else out[#out + 1] = line; line = word end
  end
  if line ~= "" or #out == 0 then out[#out + 1] = line end
  if n and #out > n then
    for i = #out, n + 1, -1 do out[i] = nil end
  end
  return out
end

function L.inside(a, b)
  return a.x >= b.x and a.y >= b.y and a.x + a.w <= b.x + b.w and a.y + a.h <= b.y + b.h
end

function L.overlap(a, b)
  return a.x < b.x + b.w and b.x < a.x + a.w and a.y < b.y + b.h and b.y < a.y + a.h
end

return L
