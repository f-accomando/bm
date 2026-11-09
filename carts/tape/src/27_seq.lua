-- bm Tape sequencer (design 3, 6.1): state, step data, clock. Step data are flat i32 buffers (vm_buf).
-- The clock follows riff: the transport remembers audio_time() at the start; each frame the notes of the
-- next LOOK seconds are asked of seq_window (engine.b) and queued with play_at, so a frame rate
-- does not move a note. The sequencer owns the voices 0..5; live playing owns 6 and 7.
local NSTEP, NSLOT, TPS, TPB = 64, 4, 6, 24      -- steps and slots per track, ticks per step and per beat
local LOOK, LEAD, CAP = 0.15, 0.06, 128          -- look-ahead s, start delay s, event records per window
local PAD_GATE = 60                              -- ms a drum pad is held before its release

local S = {
  bpm = 120, swing = 0, metro = 0, len = 16,     -- tempo page
  trk = 2, syn = 2,                              -- selected track, last synth track
  pad = 1,                                       -- last pad hit
  cur = 0, bn = 36, bv = 100, bg = 1,            -- grid cursor (0-based) and brush: note, velocity, gate in steps
  lvl = { 0.9, 0.9, 0.7, 0.7 },
}

local HAVE_BUF = vm_buf ~= nil
local function arr(n) return HAVE_BUF and vm_buf("i32", n) or {} end
local function aget(a, i)
  if HAVE_BUF then return vm_buf_get(a, i) end
  return a[i] or 0
end
local function aset(a, i, v)
  if HAVE_BUF then vm_buf_fill(a, { v }, i) else a[i] = v end
end

local Q = { notes = arr(4 * NSTEP * NSLOT), vels = arr(4 * NSTEP * NSLOT), gates = arr(4 * NSTEP * NSLOT), mutes = arr(4) }
local OUT = arr(5 * CAP)

-- the synth tracks 2..4: a bass, a chord sound, a lead (root note C2, C3, C4)
local trk = {}
for i, d in ipairs({ { 1, 36 }, { 10, 48 }, { 9, 60 } }) do
  trk[i + 1] = { root = d[2], oct = 0 }
  load_sound(trk[i + 1], d[1])
end
for _, p in ipairs(PADS) do p.tone = pad_tone(p) end

-- ---------------------------------------------------------------- step data
local function sidx(t, s, k) return ((t - 1) * NSTEP + s) * NSLOT + k + 1 end

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

-- demo pattern: a 16-step beat on track 1 (pads: 1 kick, 2 snare, 3 hat, 6 rim) and a bass line on track 2
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
end

-- ---------------------------------------------------------------- clock
-- tick k is at tr.t0 + (k - tr.tick0) * tr.spt seconds of audio_time(); 24 ticks make a beat
local tr = { run = false, rec = false, t0 = 0, start = 0, tick0 = 0, spt = 2.5 / 120, bpm = 120, sched = 0, pos = 0 }
local CLICK = { wave = "triangle", attack = 1, decay = 30, sustain = 0, release = 20 }
local at_hook, seq_skip = nil, 1               -- test build only (carts/tape/build/build.py)
--@TEST_HOOK@

local function tick_time(k) return tr.t0 + (k - tr.tick0) * tr.spt end
local function tick_at(time) return tr.tick0 + (time - tr.t0) / tr.spt end
-- the play head in ticks (0 when stopped)
local function now_tick() return tr.run and max(0, floor(tick_at(audio_time()))) or 0 end

-- one event of seq_window to play_at
local function queue(tick, t, note, vel, gate)
  local when, vol = tick_time(tick), S.lvl[t] * vel / 127
  if at_hook then at_hook(when - tr.start, t, note, vel, tick) end
  if t == 1 then
    local p = PADS[note]
    if p then play_at(when, p.tone, p.note + p.pitch, PAD_GATE, vol * p.level, 1) end
  else
    play_at(when, trk[t].tone, note, max(10, gate * tr.spt * 1000), vol, 1)
  end
end

-- queue the events and the metronome clicks of the ticks a .. b - 1
local function schedule(a, b)
  local n = engine.seq_window(a, b, S.len, S.swing, Q, OUT, CAP)
  if not n then return end
  for i = 0, n - 1 do
    local o = i * 5
    queue(aget(OUT, o + 1), aget(OUT, o + 2) + 1, aget(OUT, o + 3), aget(OUT, o + 4), aget(OUT, o + 5))
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
  tr.start, tr.spt, tr.bpm = tr.t0, 2.5 / S.bpm, S.bpm
end

local function transport_stop()
  tr.run, tr.pos = false, 0
  play_cancel(1); play_cancel(2)
end

-- once per frame (every seq_skip-th frame in the test build); now is audio_time()
local function seq_update(now)
  if not tr.run then return end
  if S.bpm ~= tr.bpm then                    -- a new tempo: the play head stays where it is
    tr.t0, tr.tick0, tr.spt, tr.bpm = now, tick_at(now), 2.5 / S.bpm, S.bpm
  end
  tr.pos = max(0, tick_at(now) * tr.spt)
  local tend = floor(tick_at(now + LOOK)) + 1
  if tend > tr.sched then
    schedule(tr.sched, tend)
    tr.sched = tend
  end
end
