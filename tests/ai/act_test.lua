-- Host tests of the assistant's actions on code (assist.act, the
-- "#entry: ... #" lines of bm Code), with the real network.
--   luaai build/assist.bin tests/ai/act_test.lua
local say = print
local fails, checks = 0, 0
local function check(ok, what, extra)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    say("FAIL " .. what .. (extra and ("\n" .. extra) or ""))
  end
end

SCREEN_W, SCREEN_H = 640, 360
function font() return 8, 16 end
local assist = dofile("src/ai/assist.lua")

local function lines_of(s)
  local t = {}
  for l in (s .. "\n"):gmatch("(.-)\n") do t[#t + 1] = l end
  if t[#t] == "" then t[#t] = nil end
  return t
end

local function find(lines, text)
  for i, l in ipairs(lines) do if l:find(text, 1, true) then return i end end
end

local CODE = [[
local speed = 2

function move(dx, dy)
  #entry: ENTRY #
  if dx > 0 then
    facing = 1
  else
    facing = -1
  end
  x = x + dx * speed
  y = y + dy * speed
  spr(1, x, y)
end

function sign(v)
  if v < 0 then return -1 else return 1 end
end
]]

local function act(req, code, at_text)
  local src = (code or CODE):gsub("ENTRY", req)
  local lines = lines_of(src)
  local at = find(lines, "#entry:")
  local r = assist.act(req, lines, at)
  return r, r and r.lines, lines
end

local function compiles(lines)
  return load(table.concat(lines, "\n"), "=t", "t", {}) ~= nil
end

-- ternary: the if/else of the function becomes "cond and a or b"
local r, out = act("aggiungi operatore ternario su questa funzione")
check(r and r.ok, "ternary ok", r and r.message)
check(out and find(out, "facing = dx > 0 and 1 or -1"), "ternary: the assignment", out and table.concat(out, "\n"))
check(out and not find(out, "#entry:"), "the #entry line is gone")
check(out and compiles(out), "ternary: compiles")
-- the one-line if with returns, in the other function (directive above it)
r, out = act("usa il ternario", [[
local a = 1
#entry: ENTRY #
function sign(v)
  if v < 0 then return -1 else return 1 end
end
]])
check(out and find(out, "return v < 0 and -1 or 1"), "ternary: return form", out and table.concat(out, "\n"))

-- comment: above the function, with the API it uses
r, out = act("commenta questa funzione")
local i = out and find(out, "-- move(dx, dy)")
check(i and out[i + 1]:find("spr", 1, true), "comment: name and uses", out and table.concat(out, "\n"))
check(out and find(out, "function move(dx, dy)") == i + 2, "comment: right above")

-- log, then remove it
r, out = act("aggiungi un log di debug")
check(out and find(out, 'log("move", "dx=", dx, "dy=", dy)'), "log line", out and table.concat(out, "\n"))
local with_log = table.concat(out, "\n"):gsub("function move%(dx, dy%)", "function move(dx, dy)\n  #entry: ENTRY #", 1)
r, out = act("togli i log", with_log)
check(r and r.ok and out and not find(out, "log("), "remove_log", r and r.message)

-- nil check, local, optimize
r, out = act("controlla i parametri nil")
check(out and find(out, "if dx == nil or dy == nil then return end"), "nil check", r and r.message)
r, out = act("rendi locale questa funzione")
check(out and find(out, "local function move(dx, dy)"), "make local", r and r.message)
r, out = act("ottimizza questa funzione")
check(out and find(out, "local spr = spr"), "optimize", out and table.concat(out, "\n"))

-- rename in the function (words only)
r, out = act("rinomina speed in velocita")
check(out and find(out, "x = x + dx * velocita") and find(out, "local speed = 2"), "rename in the function",
      out and table.concat(out, "\n"))
r, out = act("rinomina speed in velocita ovunque")
check(out and find(out, "local velocita = 2"), "rename everywhere")

-- indent a function written all flat
r, out = act("sistema l'indentazione", [[
#entry: ENTRY #
function f(a)
if a then
for i = 1, 3 do
log(i)
end
else
return {
1, 2,
}
end
end
]])
local want = [[
function f(a)
  if a then
    for i = 1, 3 do
      log(i)
    end
  else
    return {
      1, 2,
    }
  end
end]]
check(out and table.concat(out, "\n") == want, "indent", out and table.concat(out, "\n"))

-- explain: text, the code unchanged
r, out = act("cosa fa questa funzione?")
check(r and r.explain and r.explain[1]:find("move%(dx, dy%)"), "explain: header", r and r.message)
local ex = r and table.concat(r.explain, "\n") or ""
check(ex:find("spr:") and ex:find("changes %(global%): facing, x, y"), "explain: API and globals", ex)

-- comment out and back
r, out = act("commenta tutta la funzione")
check(out and find(out, "-- function move(dx, dy)") and compiles(out), "comment out")

-- an example to insert: the particles, at the #entry line
r, out = act("crea uno snippet per uno sprite particle effect", "local a = 1\n#entry: ENTRY #\n")
check(r and r.ok and out and find(out, "function boom(x, y)"), "snippet: particles", r and r.message)
check(out and compiles(out), "snippet compiles")

-- a sprite, as code that writes it into the sheet
r, out = act("crea uno sprite slime rosso", "#entry: ENTRY #\n")
check(r and r.ok and out and find(out, "local art = {") and find(out, "sset(x - 1, y - 1, c)"),
      "sprite as code", r and r.message)
check(out and compiles(out), "sprite code compiles")
local longest = 0
for _, l in ipairs(out or {}) do longest = math.max(longest, #l) end
check(longest <= 72, "sprite code fits the panel: " .. longest)

-- nothing it knows: nil
r = assist.act("che tempo fa domani a roma", { "#entry: che tempo fa domani a roma #" }, 1)
check(r == nil or (r.ok == false and r.lines[1]:find("#entry:")), "off topic: nothing done")

say(string.format("actions: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
