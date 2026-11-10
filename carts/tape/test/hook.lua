-- test build (TAPE_TEST=@N@): log each note the sequencer queues, and update the sequencer every @N@-th frame.
-- F9 builds a test song (PATTERN mode: P switches to SONG), F10 logs the state of the song (a canonical text of the saved table). Spliced in at the line
-- --@TEST_HOOK@ of src/27_seq.lua: it sees the locals above it.
seq_skip = @N@
at_hook = function(t, track, note, vel, tick, vol, pan, rev, ech, cho)
  log(string.format("TAPE_AT %.6f %d %d %d %d %.4f %.3f %.3f %.3f %.3f", t, track, note, vel, tick, vol, pan, rev, ech, cho))
end

-- a value as text, the keys in order
local function canon(v)
  if type(v) ~= "table" then return type(v) == "string" and string.format("%q", v) or tostring(v) end
  local ks = {}
  for k in pairs(v) do ks[#ks + 1] = k end
  table.sort(ks, function(a, b) return tostring(a) < tostring(b) end)
  local o = {}
  for _, k in ipairs(ks) do o[#o + 1] = tostring(k) .. "=" .. canon(v[k]) end
  return "{" .. table.concat(o, ",") .. "}"
end

-- a 4-track song of 8 slots (loop 1..6): different patterns for each track, levels, pans, sends, an edited sound
local function build_song()
  SV.clear()
  S.lin, S.lout = 1, 6
  S.bpm, S.swing = 132, 1
  local function pat(t, p, notes, vel)
    S.pat[t] = p
    for i, n in ipairs(notes) do
      if n > 0 then put(t, (i - 1) * 2 % 16, n, vel + i % 5 * 4, TPS * (1 + i % 2)) end
    end
  end
  pat(1, 1, { 1, 3, 2, 3, 1, 3, 2, 3 }, 100); pat(1, 2, { 1, 1, 2, 3, 1, 6, 2, 6 }, 90); pat(1, 3, { 1, 0, 0, 3, 2, 0, 3, 3 }, 95)
  pat(2, 1, { 36, 0, 39, 36, 41, 0, 43, 39 }, 100); pat(2, 2, { 38, 38, 0, 41, 43, 0, 45, 41 }, 96)
  pat(3, 1, { 48, 0, 52, 0, 55, 0, 52, 0 }, 80); pat(3, 4, { 50, 0, 53, 0, 57, 0, 53, 0 }, 84)
  pat(4, 1, { 60, 62, 64, 67, 64, 62, 60, 0 }, 90); pat(4, 2, { 72, 0, 69, 0, 67, 0, 64, 62 }, 92)
  local rows = { "112112", "121221", "010041", "002121" }
  for t = 1, 4 do
    for j = 1, #rows[t] do SONG.set(t, j - 1, tonumber(rows[t]:sub(j, j))) end
    S.pat[t] = 1
  end
  S.lvl, S.pan = { 0.8, 0.6, 0.9, 0.5 }, { -0.5, 0, 0.4, 1 }
  S.send = { { 0.1, 0, 0 }, { 0.3, 0.2, 0 }, { 0.2, 0.4, 0.3 }, { 0, 0.5, 0.6 } }
  S.fx.echo, S.fx.room, S.fx.chorus = { 300, 0.5, 0.4 }, { 0.7, 0.3, 0.35 }, { 1.2, 3, 0.8 }
  FX.apply()
  local t = trk[2]
  load_sound(t, 4)
  t.env[2], t.flt[1], t.mac[1], t.mod = 420, 3.3, 0.7, true
  retone(t)
  trk[4].oct = 1
  MIX.touch()
end

local f9, f10 = false, false
test_tick = function()
  local a, b = keydown(0x42), keydown(0x43)
  if a and not f9 then build_song() end
  if b and not f10 then log("TAPE_STATE " .. canon(SV.snapshot())) end
  f9, f10 = a, b
end
