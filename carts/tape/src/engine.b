-- bm Tape engine. Integer only. Arrays start at 1.

-- Euclidean rhythm: k pulses spread over n steps, then rotated by r steps.
-- Step i (0-based) holds a pulse when (i * k) % n < k. Writes 1 or 0 into out[1..n].
-- Returns the number of pulses (k clamped to 0..n). n is clamped to 1..64.
function euclid(k: i32, n: i32, r: i32, out: i32[]): i32
  local steps = max(1, min(n, 64))
  local pulses = max(0, min(k, steps))
  for i = 0, steps - 1 do
    local hit = 0
    if (i * pulses) % steps < pulses then
      hit = 1
    end
    out[(i + r) % steps + 1] = hit
  end
  return pulses
end

-- Meter fall for n meters. Units: level and peak 0..1000, dt in milliseconds.
-- levels[i]: falls 4 per 5 ms (1000 in 1250 ms), never below 0.
-- peaks[i]: the peak mark. peaks[n + i]: its hold time left in ms.
-- A level at or above the peak sets the peak and a 600 ms hold. After the hold
-- the peak falls 3 per 10 ms (1000 in 3333 ms), never below the level.
function meter_fall(levels: i32[], peaks: i32[], n: i32, dt: i32)
  for i = 1, n do
    local lv = max(0, levels[i] - dt * 4 // 5)
    levels[i] = lv
    if lv >= peaks[i] then
      peaks[i] = lv
      peaks[n + i] = 600
    elseif peaks[n + i] > 0 then
      peaks[n + i] = max(0, peaks[n + i] - dt)
    else
      peaks[i] = max(lv, peaks[i] - dt * 3 // 10)
    end
  end
end

-- Sequencer window. The step arrays hold 4 tracks x 64 steps x 4 slots, flat: the slot s (0..3) of
-- step p (0..63) of track k (0..3) is at index ((k * 64 + p) * 4 + s) + 1. A note of 0 is an empty slot.
-- 24 ticks make a beat, 6 ticks a step. The pattern is len steps long (clamped to 1..64) and loops.
-- swing (clamped to 0..3) delays every second step (p odd) by that many ticks.
-- A muted track (mutes[k + 1] ~= 0) gives no event.
-- The events that start in the ticks t0 .. t1 - 1 (absolute, from the play start) go to out as records of
-- 5 numbers: tick, track (0..3), note, velocity, gate (ticks). Order: tick, then track, then slot.
-- cap is the number of records out can hold; the function never writes more and returns the count it wrote.
function seq_window(t0: i32, t1: i32, len: i32, swing: i32, notes: i32[], vels: i32[], gates: i32[],
                    mutes: i32[], out: i32[], cap: i32): i32
  local steps = max(1, min(len, 64))
  local sw = max(0, min(swing, 3))
  local plen = steps * 6
  local n = 0
  local t = max(0, t0)
  while t < t1 and n < cap do
    local m = t % plen
    local p = m // 6
    local r = m % 6
    local want = 0
    if p % 2 == 1 then
      want = sw
    end
    if r == want then
      for k = 0, 3 do
        if mutes[k + 1] == 0 then
          for s = 0, 3 do
            local i = (k * 64 + p) * 4 + s + 1
            if notes[i] > 0 and n < cap then
              local o = n * 5
              out[o + 1] = t
              out[o + 2] = k
              out[o + 3] = notes[i]
              out[o + 4] = vels[i]
              out[o + 5] = gates[i]
              n = n + 1
            end
          end
        end
      end
    end
    t = t + 1
  end
  return n
end
