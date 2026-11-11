-- bm Tape sequencer (design 3, 6.1): state, step data, clock. Step data are flat i32 buffers (vm_buf).
-- The clock follows riff: the transport remembers audio_time() at the start; each frame the notes of the
-- next LOOK seconds are asked of song_window (engine.b) and queued with play_at, so a frame rate
-- does not move a note. The sequencer owns the voices 0..5; live playing owns 6 and 7.
-- A track has 8 patterns (A..H) of NSTEP steps with NSLOT slots. The song is a row of NSONG slots a track; a slot holds
-- a pattern number (0 = none). PATTERN mode loops the selected pattern of each track; SONG mode plays the slots lin..lout.
local NSTEP, NSLOT, NPAT, NSONG, TPS, TPB = 64, 4, 8, 64, 6, 24   -- steps, slots, patterns, song slots, ticks per step and beat
local LOOK, LEAD, CAP = 0.15, 0.06, 128          -- look-ahead s, start delay s, event records per window
local PAD_GATE = 60                              -- ms a drum pad is held before its release

local S = {
  bpm = 120, swing = 0, metro = 0, len = 16,     -- tempo page
  trk = 2, syn = 2,                              -- selected track, last synth track
  pad = 1,                                       -- last pad hit
  cur = 0, bn = 36, bv = 100, bg = 1,            -- grid cursor (0-based) and brush: note, velocity, gate in steps
  lvl = { 0.9, 0.9, 0.7, 0.7 },                  -- MIX: level and pan (-1..1) of each track
  pan = { 0, 0, 0, 0 },
  send = { { 0.16, 0, 0 }, { 0.16, 0, 0 }, { 0.16, 0, 0 }, { 0.16, 0, 0 } },   -- FX: room, echo and chorus send of each track
  fx = nil,                                      -- FX: { echo = {ms, feedback, wet}, room = {size, damp, wet}, chorus = {rate, depth, wet} }
  pat = { 1, 1, 1, 1 },                          -- the selected pattern of each track (1..8)
  song = false,                                  -- SONG mode (false: PATTERN mode)
  lin = 1, lout = 4, sc = 0,                     -- loop in and out (slots 1..64, out included), TAPE cursor slot (0-based)
}
-- the modules of later files: MIX (levels, pan, sends, meters), FX, SONG (slots), SV (save and load), PUNCH (punch-in effects)
local MIX, FX, SONG, SV, PUNCH = {}, {}, {}, {}, {}

local HAVE_BUF = vm_buf ~= nil
local function arr(n) return HAVE_BUF and vm_buf("i32", n) or {} end
local function aget(a, i)
  if HAVE_BUF then return vm_buf_get(a, i) end
  return a[i] or 0
end
local function aset(a, i, v)
  if HAVE_BUF then vm_buf_fill(a, { v }, i) else a[i] = v end
end

local Q = { notes = arr(4 * NPAT * NSTEP * NSLOT), vels = arr(4 * NPAT * NSTEP * NSLOT), gates = arr(4 * NPAT * NSTEP * NSLOT), mutes = arr(4) }
local SL = arr(4 * NSONG)                        -- the song: SL[(t - 1) * NSONG + j + 1] is the pattern of track t in slot j
local PSL = arr(4)                               -- PATTERN mode: one slot for each track
local OUT = arr(5 * CAP)

-- the synth tracks 2..4: a bass, a chord sound, a lead (root note C2, C3, C4)
local trk = {}
for i, d in ipairs({ { 1, 36 }, { 10, 48 }, { 9, 60 } }) do
  trk[i + 1] = { root = d[2], oct = 0 }
  load_sound(trk[i + 1], d[1])
end
for _, p in ipairs(PADS) do p.tone = pad_tone(p) end

-- ---------------------------------------------------------------- step data
-- the step s of the pattern pt (default: the selected one) of track t, slot k
local function sidx(t, s, k, pt) return (((t - 1) * NPAT + (pt or S.pat[t]) - 1) * NSTEP + s) * NSLOT + k + 1 end

-- the slot of step s of track t that holds note, or nil
local function find_slot(t, s, note)
  for k = 0, NSLOT - 1 do
    if aget(Q.notes, sidx(t, s, k)) == note then return k end
  end
end

-- write a note into the step: the slot of the same note, else the first empty slot, else the last slot
local function put(t, s, note, vel, gate)
  local k = find_slot(t, s, note) or find_slot(t, s, 0) or NSLOT - 1
  local i = sidx(t, s, k)
  aset(Q.notes, i, note); aset(Q.vels, i, vel); aset(Q.gates, i, gate)
end

local function clear_slot(t, s, k)
  local i = sidx(t, s, k)
  aset(Q.notes, i, 0); aset(Q.vels, i, 0); aset(Q.gates, i, 0)
end
local function clear_step(t, s) for k = 0, NSLOT - 1 do clear_slot(t, s, k) end end
local function clear_track(t) for s = 0, NSTEP - 1 do clear_step(t, s) end end

-- how many notes the step holds, and the first slot's velocity and gate
local function step_info(t, s)
  local n = 0
  for k = 0, NSLOT - 1 do
    if aget(Q.notes, sidx(t, s, k)) > 0 then n = n + 1 end
  end
  return n, aget(Q.vels, sidx(t, s, 0)), aget(Q.gates, sidx(t, s, 0))
end

local function toggle_mute(t) aset(Q.mutes, t, aget(Q.mutes, t) == 0 and 1 or 0) end

-- demo: a 16-step beat on track 1 (pads: 1 kick, 2 snare, 3 hat, 6 rim) and a bass line on track 2, both pattern A.
-- The song row holds the beat in the slots 1..4 and the bass in 2..4; the loop is 1..4.
local function load_demo()
  for _, s in ipairs({ 0, 4, 8, 10, 12 }) do put(1, s, 1, s == 10 and 80 or 120, TPS) end
  for _, s in ipairs({ 4, 12 }) do put(1, s, 2, 115, TPS) end
  for s = 0, 14, 2 do put(1, s, 3, 90, TPS) end
  for _, s in ipairs({ 3, 7, 11, 15 }) do put(1, s, 3, 50, TPS) end
  local rim = engine.euclid(5, 16, 2)
  for s = 0, 15 do
    if rim and rim[s + 1] == 1 then put(1, s, 6, 70, TPS) end
  end
  for _, b in ipairs({ { 0, 36, 3 }, { 3, 36, 2 }, { 6, 39, 2 }, { 8, 41, 3 }, { 11, 43, 2 }, { 14, 39, 2 } }) do
    put(2, b[1], b[2], 105, b[3] * TPS)
  end
  -- the multitrack demo: four instruments (drums, DX BASS, JUNO PAD, PULSE LEAD) over a song of 8 slots in C minor.
  -- Pattern A is the verse and pattern B the lift (slots 5 to 8). The drums start alone, the bass comes in at slot 2, the pad
  -- and the lead at slot 3; the lift has a busier beat and a new bass line, chords and tune. The loop is the 8 slots.
  local function in_pat(t, pt, fn)
    local keep = S.pat[t]
    S.pat[t] = pt
    fn()
    S.pat[t] = keep
  end
  local function chord(t, s, notes, vel, steps)
    for _, n in ipairs(notes) do put(t, s, n, vel, steps * TPS) end
  end
  chord(3, 0, { 48, 51, 55 }, 80, 7); chord(3, 8, { 53, 56, 60 }, 80, 7)                       -- Cm, Fm
  for _, m in ipairs({ { 0, 67, 2 }, { 3, 70, 2 }, { 6, 72, 2 }, { 8, 70, 3 }, { 11, 67, 2 }, { 14, 63, 2 } }) do
    put(4, m[1], m[2], 95, m[3] * TPS)
  end
  in_pat(1, 2, function()
    for _, st in ipairs({ 0, 3, 6, 10, 12 }) do put(1, st, 1, 120, TPS) end
    for _, st in ipairs({ 4, 12, 15 }) do put(1, st, 2, st == 15 and 70 or 115, TPS) end
    for st = 0, 15 do put(1, st, 3, st % 2 == 0 and 90 or 55, TPS) end
    put(1, 14, 6, 80, TPS)
  end)
  in_pat(2, 2, function()
    for _, b in ipairs({ { 0, 44, 3 }, { 3, 44, 2 }, { 6, 46, 2 }, { 8, 43, 3 }, { 11, 43, 2 }, { 14, 41, 2 } }) do
      put(2, b[1], b[2], 105, b[3] * TPS)
    end
  end)
  in_pat(3, 2, function()
    chord(3, 0, { 56, 60, 63 }, 80, 7); chord(3, 8, { 55, 59, 62 }, 80, 7)                   -- Ab, G
  end)
  in_pat(4, 2, function()
    for _, m in ipairs({ { 0, 72, 2 }, { 2, 75, 2 }, { 4, 77, 2 }, { 6, 79, 3 }, { 10, 77, 2 }, { 12, 75, 2 }, { 14, 74, 2 } }) do
      put(4, m[1], m[2], 100, m[3] * TPS)
    end
  end)
  local function slots(t, from, to, pt) for j = from, to do aset(SL, (t - 1) * NSONG + j + 1, pt) end end
  slots(1, 0, 3, 1); slots(1, 4, 7, 2)
  slots(2, 1, 3, 1); slots(2, 4, 7, 2)
  slots(3, 2, 3, 1); slots(3, 4, 7, 2)
  slots(4, 2, 3, 1); slots(4, 4, 7, 2)
  S.lout = 8
  S.pan = { 0, 0, -0.3, 0.3 }
  S.send[3][3], S.send[4][2] = 0.3, 0.25                                                    -- chorus on the pad, echo on the lead
end

-- ---------------------------------------------------------------- clock
-- tick k is at tr.t0 + (k - tr.tick0) * tr.spt seconds of audio_time(); 24 ticks make a beat
local tr = { run = false, rec = false, t0 = 0, start = 0, tick0 = 0, spt = 2.5 / 120, sched = 0 }
local CLICK = { wave = "triangle", attack = 1, decay = 30, sustain = 0, release = 20 }
local at_hook, seq_skip, test_tick = nil, 1, nil    -- test build only (carts/tape/build/build.py)
--@TEST_HOOK@

local function tick_time(k) return tr.t0 + (k - tr.tick0) * tr.spt end
local function tick_at(time) return tr.tick0 + (time - tr.t0) / tr.spt end
-- the play head in ticks (0 when stopped)
local function now_tick() return tr.run and max(0, floor(tick_at(audio_time()))) or 0 end

-- one event of song_window to play_at: the tone carries the pan and the sends of the track; the level and the velocity
-- make the volume. The meter of the tower rises when the note starts.
local function queue(tick, t, note, vel, gate)
  local when, vol = tick_time(tick), S.lvl[t] * vel / 127
  if at_hook then at_hook(when - tr.start, t, note, vel, tick, vol, S.pan[t], S.send[t][1], S.send[t][2], S.send[t][3]) end
  MIX.hit(t, vol, when)
  if t == 1 then
    local p = PADS[note]
    if p then play_at(when, MIX.tone(p.tone, 1, when), p.note + p.pitch, PAD_GATE, vol * p.level, 1) end
  else
    play_at(when, MIX.tone(trk[t].tone, t, when), note, max(10, gate * tr.spt * 1000), vol, 1)
  end
end

-- the slot table of the mode: slots, stride, loop start and end (slots, end excluded)
local function slot_args()
  if S.song then return SL, NSONG, S.lin - 1, S.lout end
  for k = 1, 4 do aset(PSL, k, S.pat[k]) end
  return PSL, 1, 0, 1
end

-- the events of the ticks a .. b - 1 in OUT; returns their number (nil without engine)
local function window(a, b)
  local sl, stride, ls, le = slot_args()
  return engine.song_window(a, b, S.len, S.swing, sl, stride, ls, le, Q, OUT, CAP)
end

-- queue the events and the metronome clicks of the ticks a .. b - 1; a punch-in stutter repeats its window instead
local function schedule(a, b)
  if PUNCH.stut then
    PUNCH.stutter(a, b, queue)
  else
    local n = window(a, b)
    if not n then return end
    for i = 0, n - 1 do
      local o = i * 5
      queue(aget(OUT, o + 1), aget(OUT, o + 2) + 1, aget(OUT, o + 3), aget(OUT, o + 4), aget(OUT, o + 5))
    end
  end
  if S.metro > 0 then
    for k = (a + TPB - 1) // TPB * TPB, b - 1, TPB do
      play_at(tick_time(k), CLICK, k % (4 * TPB) == 0 and 84 or 72, 30, S.metro * 0.6, 2)
    end
  end
end

local function transport_start()
  if not engine.mod then return end
  tr.run, tr.t0, tr.tick0, tr.sched = true, audio_time() + LEAD, 0, 0
  tr.start, tr.spt = tr.t0, PUNCH.spt()
end

local function transport_stop()
  tr.run = false
  PUNCH.stut = nil
  MIX.hits = {}
  play_cancel(1); play_cancel(2)
end

-- the new seconds a tick lasts from the tick tr.sched on; the notes that are queued keep their times, so the clock has no jump
local function retime(spt)
  tr.t0, tr.tick0, tr.spt = tick_time(tr.sched), tr.sched, spt
end

-- once per frame (every seq_skip-th frame in the test build); now is audio_time()
local function seq_update(now)
  if not tr.run then return end
  local want = PUNCH.spt()
  if want ~= tr.spt then retime(want) end        -- a new tempo or half speed: the play head stays where it is
  local tend = floor(tick_at(now + LOOK)) + 1
  if tend > tr.sched then
    schedule(tr.sched, tend)
    tr.sched = tend
  end
end
