-- bm Tape frame (design 4.1): status strip on top, value strip at the bottom.
local BPM = 120
local STEP_S = 60 / BPM / 4                      -- one 16th step, in seconds
local tr = { run = false, pos = 0 }              -- transport: pos in seconds

local function tr_step() return floor(tr.pos / STEP_S) end

local function fmt_param(p)
  local f = p.format
  if type(f) == "function" then return f(p.value) end
  return string.format(f, p.value)
end

-- track number: the TAPE scene owns it (its 4th encoder)
local function track_no(scenes) return floor(scenes[6].params[4].value + 0.5) end

local function frame(sc, scenes, t)
  -- status strip
  T(sc.name, 6, 9, C.e4)
  if engine.err then
    T("ENGINE " .. engine.err:sub(1, 33), 118, 9, C.hot)
  else
    T("T" .. track_no(scenes), 118, 9, C.mute)
    T(string.format("%.1f BPM", BPM), 150, 9, C.mute)
    local st = tr_step()
    T(string.format("%02d.%d.%d", 1 + (st >> 4), 1 + ((st >> 2) & 3), 1 + (st & 3)), 214, 9, tr.run and C.e4 or C.mute)
  end
  for i = 0, 7 do
    R(264 + i * 6, 4, 4, 5, (i * 7 + floor(t * 4)) % 5 < 2 and C.hot or C.dim, true)
  end
  L(0, 13, 320, 13, C.dim)
  L(0, BOT + 2, 320, BOT + 2, C.dim)
  -- value strip: one cell per encoder
  for i, p in ipairs(sc.params) do
    T(p.label, 6 + (i - 1) * 79, 146, ECOL[i])
    T(fmt_param(p), 6 + (i - 1) * 79, 155, C.mute)
  end
end
