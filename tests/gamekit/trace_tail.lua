
-- Appended to the end of a game's main.lua by the Makefile (make test-game-trace).
-- It prints one digest per frame of everything the game draws, plays and saves,
-- so two versions of a game can be compared without knowing their variables.
local _tr = { n = 0, frame = 0 }
local function _str(v)                  -- a table by its sorted fields (an address would change)
  if type(v) ~= "table" then return tostring(v) end
  local keys = {}
  for k in pairs(v) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  local out = {}
  for _, k in ipairs(keys) do out[#out + 1] = tostring(k) .. "=" .. _str(v[k]) end
  return "{" .. table.concat(out, ";") .. "}"
end
local function _mix(name, ...)
  local s = name
  for i = 1, select("#", ...) do s = s .. "," .. _str((select(i, ...))) end
  _tr.n = _tr.n + 1
  _tr[_tr.n] = s
end
for _, name in ipairs({ "cls", "rectfill", "rect", "print", "pset", "spr", "line", "circfill",
                        "note", "freq", "duty", "envelope", "volume", "sfx", "play", "save" }) do
  local f = _G[name]
  if f then _G[name] = function(...) _mix(name, ...); return f(...) end end
end
local _u, _d = _update, _draw
function _update() _tr.frame = _tr.frame + 1; _u() end
function _draw()
  _d()
  local h = 5381
  for i = 1, _tr.n do
    local s = _tr[i]
    for k = 1, #s do h = (h * 33 + s:byte(k)) % 4294967296 end
    _tr[i] = nil
  end
  log(string.format("trace %d %d %08x", _tr.frame, _tr.n, h))
  _tr.n = 0
end
