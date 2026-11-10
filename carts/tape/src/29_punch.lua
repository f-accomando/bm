-- bm Tape punch-in effects (design 3, 5): hold Z stutter, X half speed, C filter sweep, V crush.
-- They act on the notes the sequencer queues while the key is down (and on live notes): the first note that feels a key
-- is the first one that is not queued yet, 150 ms (LOOK) ahead at most. C and V also write the registers of the
-- voices with apu(), so a note that sounds changes within a frame; the note that is queued but has not started
-- begins with its own tone and takes the change in the next frame.
-- private helpers: the file is one block
do
PUNCH.CRUSH_BITS, PUNCH.CRUSH_COARSE = 5, 2        -- V: bits kept and rate divisor
local SWEEP_FROM, SWEEP_TO, SWEEP_S = 12000, 180, 1.2   -- C: the cutoff falls from 12 kHz to 180 Hz in 1.2 s
PUNCH.slow, PUNCH.sweep, PUNCH.crush, PUNCH.stut = false, false, false, nil
PUNCH.t0 = 0

-- seconds a tick lasts: the tempo, twice as long at half speed
function PUNCH.spt() return 2.5 / S.bpm * (PUNCH.slow and 2 or 1) end

-- the cutoff (Hz) of the sweep at the time when (audio_time)
function PUNCH.cutoff(when)
  return SWEEP_FROM * (SWEEP_TO / SWEEP_FROM) ^ clamp((when - PUNCH.t0) / SWEEP_S, 0, 1)
end

-- the register value of a cutoff in Hz (as presets.c: 1..255 = 20 Hz .. 20 kHz)
local function cutoff8(hz) return clamp(floor(1.5 + math.log(hz / 20, 2) * (254 / 9.9658)), 1, 255) end

-- the stutter: the events of the last 6 ticks that are queued, repeated every 6 ticks from tick base on
function PUNCH.start_stutter()
  local base = tr.sched
  local from = max(0, base - TPS)
  local n = tr.run and window(from, base) or 0
  local ev = {}
  for i = 0, (n or 0) - 1 do
    local o = i * 5
    ev[#ev + 1] = { aget(OUT, o + 1) - from, aget(OUT, o + 2) + 1, aget(OUT, o + 3), aget(OUT, o + 4), aget(OUT, o + 5) }
  end
  PUNCH.stut = { base = base, ev = ev }
end

-- the events of the ticks a .. b - 1 of the stutter (a is never before its base), to the queue function q
function PUNCH.stutter(a, b, q)
  local st = PUNCH.stut
  for j = (a - st.base) // TPS, (b - 1 - st.base) // TPS do
    for _, e in ipairs(st.ev) do
      local tick = st.base + j * TPS + e[1]
      if tick >= a and tick < b then q(tick, e[2], e[3], e[4], e[5]) end
    end
  end
end

-- once per frame after poll(): the four keys
function PUNCH.update(now_t)
  if ctrl() then return end
  if pressed(K.Z) then PUNCH.start_stutter() end
  if released(K.Z) then PUNCH.stut = nil end
  PUNCH.slow = held[K.X] or false
  if pressed(K.C) then PUNCH.sweep, PUNCH.t0 = true, now_t end
  if released(K.C) then PUNCH.sweep = false end
  if pressed(K.V) or released(K.V) then PUNCH.crush = held[K.V] or false; MIX.touch() end
  if PUNCH.sweep then                              -- the voices that sound follow the sweep now
    local c8 = cutoff8(PUNCH.cutoff(now_t))
    for v = 0, 7 do
      local cur = apu(v, 11)
      apu(v, 11, cur == 0 and c8 or min(cur, c8))
    end
  end
  if PUNCH.crush then
    for v = 0, 7 do apu(v, 29, (PUNCH.CRUSH_COARSE - 1) * 16 + PUNCH.CRUSH_BITS) end
  end
end

-- the label of the active effects on the status strip, or nil
function PUNCH.label()
  if PUNCH.stut then return "STUTTER" end
  if PUNCH.slow then return "HALF" end
  if PUNCH.sweep then return "SWEEP" end
  if PUNCH.crush then return "CRUSH" end
end
end
