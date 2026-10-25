-- Plays the input scripts of scripts/smw_trace.py through the physics and logs one line per tick.
local SCRIPTS = {
  { "walk",    { { 20, "" }, { 90, "R" }, { 40, "" } } },
  { "run",     { { 20, "" }, { 150, "RY" }, { 60, "" } } },
  { "jump",    { { 20, "" }, { 50, "B" }, { 60, "" } } },
  { "runjump", { { 20, "" }, { 100, "RY" }, { 60, "RYB" }, { 60, "RY" }, { 60, "" } } },
  { "turn",    { { 20, "" }, { 60, "R" }, { 40, "L" }, { 30, "" } } },
}

local function solid(px, py) return py >= 384 end

function _init()
  for _, s in ipairs(SCRIPTS) do
    local m = Phys.new(16, 352)
    local f = 0
    for _, seg in ipairs(s[2]) do
      local k = seg[2]
      local inp = { left = k:find("L") ~= nil, right = k:find("R") ~= nil,
                    run = k:find("Y") ~= nil, jump = k:find("B") ~= nil }
      for _ = 1, seg[1] do
        Phys.step(m, inp, solid)
        log(string.format("TRACE %s %d %.2f %.2f %.2f %.2f %.1f", s[1], f, m.x, m.y, m.vx, m.vy, m.pm))
        f = f + 1
      end
    end
  end
  quit()
end

function _update() end
function _draw() end
