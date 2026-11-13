-- bm Tape playing, recording and step edit. The 12 keys of the KO-II block are the pads of the drum track
-- or 12 semitones above the root of a synth track. Live notes use the voices 6 and 7 in turn. A live note has the
-- level, the pan and the sends of its track, as a note of the sequencer.
-- the piano: Z S X D C V G B H N J M are C to B (the white keys are the bottom row), then , L . ; / are C to E above
local PADK = { K.Z, K.S, K.X, K.D, K.C, K.V, K.G, K.B, K.H, K.N, K.J, K.M, K.COMMA, K.L, K.DOT, K.SEMI, K.SLASH }
local BLACK = { [2] = true, [4] = true, [7] = true, [9] = true, [11] = true, [14] = true, [16] = true }
local live = { n = 0, held = {}, rec = {} }       -- held[i] = voice of key i; rec[i] = the recorded slot of key i

-- the voice for a new live note: 6 or 7 in turn, the one that is not held by a key if there is one
local function live_voice()
  for k = 0, 1 do
    local v = 6 + (live.n + k) % 2
    local busy = false
    for _, h in pairs(live.held) do busy = busy or h == v end
    if not busy or k == 1 then
      live.n = live.n + k + 1
      for i, h in pairs(live.held) do
        if h == v then live.held[i] = nil end
      end
      return v
    end
  end
end

-- the note of key i (1..12) on synth track t
local function key_note(t, i) return clamp(trk[t].root + trk[t].oct * 12 + i - 1, 1, 127) end

-- a hit while the transport plays and record is armed: nearest step; the last half step goes to step 1
local function rec_hit(t, note, vel, i)
  if not (tr.run and tr.rec) then return end
  local tk = tick_at(audio_time())
  local s = floor((tk % (S.len * TPS)) / TPS + 0.5)
  if s >= S.len then s = 0 end
  put(t, s, note, vel, TPS)
  if t > 1 then live.rec[i] = { t = t, s = s, note = note, vel = vel, tk = tk } end
end

local function key_on(i)
  local t = S.trk
  if t == 1 and not PADS[i] then return end        -- the drum kit has 12 pads: the keys above the first octave are silent
  local v = live_voice()
  if t == 1 then
    local p = PADS[i]
    S.pad = i
    play(v, MIX.tone(p.tone, 1, audio_time()), p.note + p.pitch, PAD_GATE, S.lvl[1] * p.level)
    MIX.hit(1, S.lvl[1])
    rec_hit(1, i, 110, i)
  else
    local note = key_note(t, i)
    play(v, MIX.tone(trk[t].tone, t, audio_time()), note, 0, S.lvl[t])
    MIX.hit(t, S.lvl[t])
    live.held[i] = v
    rec_hit(t, note, 100, i)
  end
end

local function key_off(i)
  local v = live.held[i]
  if v then noteoff(v) end
  live.held[i] = nil
  local r = live.rec[i]                           -- the gate of a recorded note is the time the key was held
  if r then
    put(r.t, r.s, r.note, r.vel, clamp(floor(tick_at(audio_time()) - r.tk + 0.5), TPS, 96))
    live.rec[i] = nil
  end
end

local function live_release()
  for i in pairs(live.held) do key_off(i) end
end

-- the brush of the grid: the note (or pad) of a new step
local function reset_brush()
  S.bn = S.trk == 1 and S.pad or key_note(S.trk, 1)
end

local function select_track(n)
  live_release()
  S.trk = n
  if n > 1 then S.syn = n end
  reset_brush()
end

-- sound the brush once
local function audition()
  local t, v = S.trk, 6 + live.n % 2
  live.n = live.n + 1
  if t == 1 then
    local p = PADS[S.bn]
    if p then play(v, MIX.tone(p.tone, 1, audio_time()), p.note + p.pitch, PAD_GATE, S.lvl[1] * p.level) end
  else
    play(v, MIX.tone(trk[t].tone, t, audio_time()), S.bn, 250, S.lvl[t])
  end
end

-- T on the SEQ GRID page: take the brush note out of the step, or put it in
local function grid_toggle()
  local t, s = S.trk, S.cur
  local k = find_slot(t, s, S.bn)
  if k then
    clear_slot(t, s, k)
  else
    put(t, s, S.bn, S.bv, S.bg * TPS)
    audition()
  end
end

-- the octave of a synth track
local function octave(d)
  if S.trk > 1 then
    live_release()
    trk[S.trk].oct = clamp(trk[S.trk].oct + d, -3, 3)
    reset_brush()
  end
end

-- the previous or next of the sounds (20 synth sounds, then the 24 PCM sounds) on the selected synth track
local function next_sound(d)
  local t = trk[S.syn]
  load_sound(t, ((t.snd == 0 and 1 or t.snd) - 1 + d) % #SOUNDS + 1)
end

-- keys of the block and the track keys; called once per frame after poll()
local function play_keys(allow)
  for i, u in ipairs(PADK) do
    if released(u) then key_off(i) end
    if allow and pressed(u) then key_on(i) end
  end
end
