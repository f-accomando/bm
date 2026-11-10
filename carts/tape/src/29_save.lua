-- bm Tape save and load (design 9): one song state in the save slot 1 (save() and saved(), as carts/ko2 does).
-- The file is a Lua table with a magic string and a version. Patterns are text lines ("step,slot,note,velocity,gate;") of the
-- used patterns only, and each song row is a string of digits that stops at its last used slot, so a typical file is small.
-- A file with another magic, another version or a wrong shape is refused and the demo song starts.
-- private helpers: the file is one block
do
local MAGIC, VERSION = "BMTAPE", 1
local MSG_SECS = 2

-- a short message on the status strip (2 s): text, and whether it is an error
SV.msg, SV.err, SV.until_t = nil, false, 0
function SV.say(text, err)
  SV.msg, SV.err, SV.until_t = text, err or false, time() + MSG_SECS
end

local function azero(a, n)
  if HAVE_BUF then
    local z = {}
    for i = 1, n do z[i] = 0 end
    vm_buf_fill(a, z, 1)
  else
    for i = 1, n do a[i] = 0 end
  end
end

-- ---------------------------------------------------------------- the state as a table
local function pat_text(t, pt)
  local o = {}
  for s = 0, NSTEP - 1 do
    for k = 0, NSLOT - 1 do
      local i = sidx(t, s, k, pt)
      local n = aget(Q.notes, i)
      if n > 0 then o[#o + 1] = string.format("%d,%d,%d,%d,%d;", s, k, n, aget(Q.vels, i), aget(Q.gates, i)) end
    end
  end
  return table.concat(o)
end

local function row_text(t)
  local last = -1
  for j = 0, NSONG - 1 do
    if SONG.get(t, j) > 0 then last = j end
  end
  local o = {}
  for j = 0, last do o[#o + 1] = tostring(SONG.get(t, j)) end
  return table.concat(o)
end

local function copy(a) return { table.unpack(a) } end

function SV.snapshot()
  local pats, rows, snd, pads, mute = {}, {}, {}, {}, {}
  for t = 1, 4 do
    rows[t], mute[t] = row_text(t), aget(Q.mutes, t)
    for pt = 1, NPAT do
      local x = pat_text(t, pt)
      if x ~= "" then pats[(t - 1) * NPAT + pt] = x end
    end
  end
  for i = 2, 4 do
    local t = trk[i]
    snd[i - 1] = { snd = t.snd, eng = t.eng, mod = t.mod, oct = t.oct, sample = t.sample, mac = copy(t.mac), env = copy(t.env), flt = copy(t.flt) }
  end
  for i, p in ipairs(PADS) do pads[i] = { p.begin, p.rel, p.pitch, p.level } end
  return {
    magic = MAGIC, ver = VERSION, bpm = S.bpm, swing = S.swing, len = S.len, metro = S.metro, song = S.song, lin = S.lin, lout = S.lout,
    lvl = copy(S.lvl), pan = copy(S.pan), send = { copy(S.send[1]), copy(S.send[2]), copy(S.send[3]), copy(S.send[4]) },
    sel = copy(S.pat), mute = mute, snd = snd, pads = pads, pats = pats, rows = rows,
    fx = { room = copy(S.fx.room), echo = copy(S.fx.echo), chorus = copy(S.fx.chorus) },
  }
end

-- ---------------------------------------------------------------- the table into the state
local function num(v, lo, hi)
  if type(v) ~= "number" or v ~= v then error("a number is wrong") end
  return clamp(v, lo, hi)
end
local function nums(d, n, lo, hi)
  if type(d) ~= "table" then error("a list is missing") end
  local o = {}
  for i = 1, n do o[i] = num(d[i], lo, hi) end
  return o
end

-- the demo, or any state: every part back to the start values
function SV.clear()
  azero(Q.notes, 4 * NPAT * NSTEP * NSLOT); azero(Q.vels, 4 * NPAT * NSTEP * NSLOT); azero(Q.gates, 4 * NPAT * NSTEP * NSLOT)
  azero(Q.mutes, 4); azero(SL, 4 * NSONG)
  S.bpm, S.swing, S.len, S.metro, S.song, S.lin, S.lout, S.sc = 120, 0, 16, 0, false, 1, 4, 0
  S.lvl, S.pan, S.pat = { 0.9, 0.9, 0.7, 0.7 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 }
  S.send = { { 0.16, 0, 0 }, { 0.16, 0, 0 }, { 0.16, 0, 0 }, { 0.16, 0, 0 } }
  for i, d in ipairs({ { 1, 36 }, { 10, 48 }, { 9, 60 } }) do
    trk[i + 1].oct = 0
    load_sound(trk[i + 1], d[1])
  end
  for _, p in ipairs(PADS) do
    p.begin, p.rel, p.pitch, p.level = 0, p.rel0, 0, 0.9
    p.tone = pad_tone(p)
  end
  MIX.touch()
  FX.reset()
end

function SV.demo() SV.clear(); load_demo() end

local function apply(d)
  SV.clear()
  S.bpm, S.swing, S.len = num(d.bpm, 60, 200), num(d.swing, 0, 3), floor(num(d.len, 16, 64) / 16) * 16
  S.metro, S.song = num(d.metro, 0, 1), d.song == true
  S.lin = num(d.lin, 1, NSONG)
  S.lout = num(d.lout, S.lin, NSONG)
  S.lvl, S.pan = nums(d.lvl, 4, 0, 1), nums(d.pan, 4, -1, 1)
  S.pat = nums(d.sel, 4, 1, NPAT)
  for t = 1, 4 do
    S.send[t] = nums(d.send[t], 3, 0, 1)
    aset(Q.mutes, t, num(d.mute[t], 0, 1) ~= 0 and 1 or 0)
    local row = d.rows[t]
    if type(row) ~= "string" or #row > NSONG then error("a song row is wrong") end
    for j = 1, #row do SONG.set(t, j - 1, num(tonumber(row:sub(j, j)), 0, NPAT)) end
    for pt = 1, NPAT do
      local x = d.pats[(t - 1) * NPAT + pt]
      if x then
        if type(x) ~= "string" then error("a pattern is wrong") end
        for s, k, n, v, g in x:gmatch("(%d+),(%d+),(%d+),(%d+),(%d+);") do
          s, k = num(tonumber(s), 0, NSTEP - 1), num(tonumber(k), 0, NSLOT - 1)
          local i = sidx(t, s, k, pt)
          aset(Q.notes, i, num(tonumber(n), 1, 127)); aset(Q.vels, i, num(tonumber(v), 1, 127)); aset(Q.gates, i, num(tonumber(g), 1, 256))
        end
      end
    end
  end
  for i = 2, 4 do
    local r, t = d.snd[i - 1], trk[i]
    if type(r) ~= "table" then error("a sound is wrong") end
    t.snd, t.eng, t.mod, t.oct = num(r.snd, 0, #SOUNDS), num(r.eng, 1, #ENGINES), r.mod == true, num(r.oct, -3, 3)
    t.sample = type(r.sample) == "string" and r.sample or nil
    t.mac, t.env, t.flt = nums(r.mac, 4, -1000, 100000), nums(r.env, 4, 0, 5000), nums(r.flt, 4, 0, 100000)
    retone(t)
  end
  for i, p in ipairs(PADS) do
    local r = d.pads[i]
    p.begin, p.rel, p.pitch, p.level = num(r[1], 0, 1), num(r[2], 10, 2000), num(r[3], -12, 12), num(r[4], 0, 1)
    p.tone = pad_tone(p)
  end
  for n, hi in pairs({ room = { 1, 1, 1 }, echo = { 1000, 1, 1 }, chorus = { 10, 10, 1 } }) do
    S.fx[n] = {}
    for i = 1, 3 do S.fx[n][i] = num(d.fx[n][i], 0, hi[i]) end
  end
  MIX.touch()
  FX.apply()
end

-- the state of a saved table; false and the reason if it is refused (the state is then the demo)
function SV.restore(d)
  if type(d) ~= "table" or d.magic ~= MAGIC then return false, "NOT A TAPE SONG" end
  if d.ver ~= VERSION then return false, "SONG VERSION " .. tostring(d.ver) end
  local ok, why = pcall(apply, d)
  if not ok then
    SV.demo()
    return false, "SONG FILE BAD"
  end
  return true
end

-- ---------------------------------------------------------------- the file
function SV.save()
  local ok, res, why = pcall(save, SV.snapshot())
  if ok and res then
    SV.say("SAVED")
    return true
  end
  SV.say(("SAVE " .. tostring(ok and why or res)):sub(1, 30), true)
  return false
end

-- at the start: the saved song, else the demo
function SV.load()
  local ok, d = pcall(saved)
  if ok and d ~= nil then
    local good, why = SV.restore(d)
    if good then return end
    SV.demo()
    SV.say(why, true)
    return
  end
  SV.demo()
  if next(saves() or {}) ~= nil then SV.say("SONG FILE BAD", true) end
end

-- Ctrl+N: a new song (the demo) when the key is pressed twice within 2 s
SV.ask_t = -10
function SV.new()
  local now = time()
  if now - SV.ask_t <= MSG_SECS then
    transport_stop()
    SV.demo()
    SV.ask_t = -10
    SV.say("NEW SONG")
  else
    SV.ask_t = now
    SV.say("NEW SONG? PRESS CTRL+N AGAIN", true)
  end
end
end
